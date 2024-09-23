"""add generator_data to ps_subscription_persistence

Revision ID: 801b9fced8b7
Revises: 655054a68ad5
Create Date: 2024-09-23 16:44:55.360055

"""

# revision identifiers, used by Alembic.
revision = '801b9fced8b7'
down_revision = '655054a68ad5'

from alembic import op
import sqlalchemy as sa


def upgrade():
    with op.batch_alter_table('ps_subscription_persistence') as batch_op:
        batch_op.add_column(sa.Column('generator_data', sa.UnicodeText))


def downgrade():
    with op.batch_alter_table('ps_subscription_persistence') as batch_op:
        batch_op.drop_column('generator_data')
