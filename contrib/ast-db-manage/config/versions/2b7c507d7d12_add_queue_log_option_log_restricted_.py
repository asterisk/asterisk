"""add queue_log option log-restricted-caller-id

Revision ID: 2b7c507d7d12
Revises: bd9c5159c7ea
Create Date: 2024-06-12 17:00:16.841343

"""

# revision identifiers, used by Alembic.
revision = '2b7c507d7d12'
down_revision = 'bd9c5159c7ea'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]


def upgrade():
    # Create the new enum
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)
    if op.get_context().bind.dialect.name == 'postgresql':
        ast_bool_values.create(op.get_bind(), checkfirst=False)

    op.add_column('queues', sa.Column('log_restricted_caller_id', ast_bool_values))


def downgrade():
    op.drop_column('queues', 'log_restricted_caller_id')
