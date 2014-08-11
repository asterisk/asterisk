"""Create queue_rules

Revision ID: d39508cb8d8
Revises: 5139253c0423
Create Date: 2014-08-10 17:27:32.973571

"""

# revision identifiers, used by Alembic.
revision = 'd39508cb8d8'
down_revision = '5139253c0423'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.create_table(
        'queue_rules',
        sa.Column('rule_name', sa.String(80), nullable=False),
        sa.Column('time', sa.String(32), nullable=False),
        sa.Column('min_penalty', sa.String(32), nullable=False),
        sa.Column('max_penalty', sa.String(32), nullable=False)
    )


def downgrade():
    ########################## drop tables ###########################

    op.drop_table('queue_rules')

