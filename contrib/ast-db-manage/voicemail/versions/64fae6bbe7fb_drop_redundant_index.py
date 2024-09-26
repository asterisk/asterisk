"""Drop redundant index

Revision ID: 64fae6bbe7fb
Revises: 1c55c341360f
Create Date: 2024-09-26 16:17:12.732445

"""

# revision identifiers, used by Alembic.
revision = '64fae6bbe7fb'
down_revision = '1c55c341360f'

from alembic import op
import sqlalchemy as sa


def upgrade():
    with op.batch_alter_table('voicemail_messages') as batch_op:
        batch_op.drop_index('voicemail_messages_dir')


def downgrade():
    with op.batch_alter_table('voicemail_messages') as batch_op:
        batch_op.create_index('voicemail_messages_dir', ['dir'])
